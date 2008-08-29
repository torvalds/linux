#ifndef P54_H
#define P54_H

/*
 * Shared defines for all mac80211 Prism54 code
 *
 * Copyright (c) 2006, Michael Wu <flamingice@sourmilk.net>
 *
 * Based on the islsm (softmac prism54) driver, which is:
 * Copyright 2004-2006 Jean-Baptiste Note <jbnote@gmail.com>, et al.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

enum control_frame_types {
	P54_CONTROL_TYPE_FILTER_SET = 0,
	P54_CONTROL_TYPE_CHANNEL_CHANGE,
	P54_CONTROL_TYPE_FREQDONE,
	P54_CONTROL_TYPE_DCFINIT,
	P54_CONTROL_TYPE_FREEQUEUE = 7,
	P54_CONTROL_TYPE_TXDONE,
	P54_CONTROL_TYPE_PING,
	P54_CONTROL_TYPE_STAT_READBACK,
	P54_CONTROL_TYPE_BBP,
	P54_CONTROL_TYPE_EEPROM_READBACK,
	P54_CONTROL_TYPE_LED
};

struct p54_control_hdr {
	__le16 magic1;
	__le16 len;
	__le32 req_id;
	__le16 type;	/* enum control_frame_types */
	u8 retry1;
	u8 retry2;
	u8 data[0];
} __attribute__ ((packed));

#define EEPROM_READBACK_LEN (sizeof(struct p54_control_hdr) + 4 /* p54_eeprom_lm86 */)
#define MAX_RX_SIZE (IEEE80211_MAX_RTS_THRESHOLD + sizeof(struct p54_control_hdr) + 20 /* length of struct p54_rx_hdr */ + 16 )

#define ISL38XX_DEV_FIRMWARE_ADDR 0x20000

struct p54_common {
	u32 rx_start;
	u32 rx_end;
	struct sk_buff_head tx_queue;
	void (*tx)(struct ieee80211_hw *dev, struct p54_control_hdr *data,
		   size_t len, int free_on_tx);
	int (*open)(struct ieee80211_hw *dev);
	void (*stop)(struct ieee80211_hw *dev);
	int mode;
	u16 seqno;
	struct mutex conf_mutex;
	u8 mac_addr[ETH_ALEN];
	u8 bssid[ETH_ALEN];
	struct pda_iq_autocal_entry *iq_autocal;
	unsigned int iq_autocal_len;
	struct pda_channel_output_limit *output_limit;
	unsigned int output_limit_len;
	struct pda_pa_curve_data *curve_data;
	__le16 rxhw;
	u8 version;
	unsigned int tx_hdr_len;
	void *cached_vdcf;
	unsigned int fw_var;
	struct ieee80211_tx_queue_stats tx_stats[8];
};

int p54_rx(struct ieee80211_hw *dev, struct sk_buff *skb);
void p54_parse_firmware(struct ieee80211_hw *dev, const struct firmware *fw);
int p54_parse_eeprom(struct ieee80211_hw *dev, void *eeprom, int len);
void p54_fill_eeprom_readback(struct p54_control_hdr *hdr);
struct ieee80211_hw *p54_init_common(size_t priv_data_len);
void p54_free_common(struct ieee80211_hw *dev);

#endif /* P54_H */
