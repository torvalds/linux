/*
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
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
};

static inline void rt2800_register_read(struct rt2x00_dev *rt2x00dev,
					const unsigned int offset,
					u32 *value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_read(rt2x00dev, offset, value);
}

static inline void rt2800_register_read_lock(struct rt2x00_dev *rt2x00dev,
					     const unsigned int offset,
					     u32 *value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_read_lock(rt2x00dev, offset, value);
}

static inline void rt2800_register_write(struct rt2x00_dev *rt2x00dev,
					 const unsigned int offset,
					 u32 value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_write(rt2x00dev, offset, value);
}

static inline void rt2800_register_write_lock(struct rt2x00_dev *rt2x00dev,
					      const unsigned int offset,
					      u32 value)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_write_lock(rt2x00dev, offset, value);
}

static inline void rt2800_register_multiread(struct rt2x00_dev *rt2x00dev,
					     const unsigned int offset,
					     void *value, const u32 length)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_multiread(rt2x00dev, offset, value, length);
}

static inline void rt2800_register_multiwrite(struct rt2x00_dev *rt2x00dev,
					      const unsigned int offset,
					      const void *value,
					      const u32 length)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	rt2800ops->register_multiwrite(rt2x00dev, offset, value, length);
}

static inline int rt2800_regbusy_read(struct rt2x00_dev *rt2x00dev,
				      const unsigned int offset,
				      const struct rt2x00_field32 field,
				      u32 *reg)
{
	const struct rt2800_ops *rt2800ops = rt2x00dev->priv;

	return rt2800ops->regbusy_read(rt2x00dev, offset, field, reg);
}

void rt2800_mcu_request(struct rt2x00_dev *rt2x00dev,
			const u8 command, const u8 token,
			const u8 arg0, const u8 arg1);

void rt2800_write_txwi(struct sk_buff *skb, struct txentry_desc *txdesc);
void rt2800_process_rxwi(struct sk_buff *skb, struct rxdone_entry_desc *txdesc);

extern const struct rt2x00debug rt2800_rt2x00debug;

int rt2800_rfkill_poll(struct rt2x00_dev *rt2x00dev);
int rt2800_config_shared_key(struct rt2x00_dev *rt2x00dev,
			     struct rt2x00lib_crypto *crypto,
			     struct ieee80211_key_conf *key);
int rt2800_config_pairwise_key(struct rt2x00_dev *rt2x00dev,
			       struct rt2x00lib_crypto *crypto,
			       struct ieee80211_key_conf *key);
void rt2800_config_filter(struct rt2x00_dev *rt2x00dev,
			  const unsigned int filter_flags);
void rt2800_config_intf(struct rt2x00_dev *rt2x00dev, struct rt2x00_intf *intf,
			struct rt2x00intf_conf *conf, const unsigned int flags);
void rt2800_config_erp(struct rt2x00_dev *rt2x00dev, struct rt2x00lib_erp *erp);
void rt2800_config_ant(struct rt2x00_dev *rt2x00dev, struct antenna_setup *ant);
void rt2800_config(struct rt2x00_dev *rt2x00dev,
		   struct rt2x00lib_conf *libconf,
		   const unsigned int flags);
void rt2800_link_stats(struct rt2x00_dev *rt2x00dev, struct link_qual *qual);
void rt2800_reset_tuner(struct rt2x00_dev *rt2x00dev, struct link_qual *qual);
void rt2800_link_tuner(struct rt2x00_dev *rt2x00dev, struct link_qual *qual,
		       const u32 count);

int rt2800_init_registers(struct rt2x00_dev *rt2x00dev);
int rt2800_init_bbp(struct rt2x00_dev *rt2x00dev);
int rt2800_init_rfcsr(struct rt2x00_dev *rt2x00dev);
int rt2800_wait_wpdma_ready(struct rt2x00_dev *rt2x00dev);

int rt2800_efuse_detect(struct rt2x00_dev *rt2x00dev);
void rt2800_read_eeprom_efuse(struct rt2x00_dev *rt2x00dev);
int rt2800_validate_eeprom(struct rt2x00_dev *rt2x00dev);
int rt2800_init_eeprom(struct rt2x00_dev *rt2x00dev);
int rt2800_probe_hw_mode(struct rt2x00_dev *rt2x00dev);

extern const struct ieee80211_ops rt2800_mac80211_ops;

#endif /* RT2800LIB_H */
