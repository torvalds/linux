/*
 *  Copyright (c) 2009- 2012 Espressif System.
 *
 */
#ifndef _ESP_CTRL_H_
#define _ESP_CTRL_H_

int sip_send_loopback_mblk(struct esp_sip *sip, int txpacket_len, int rxpacket_len, int packet_id);

int sip_send_config(struct esp_pub *epub, struct ieee80211_conf * conf);

int sip_send_setkey(struct esp_pub *epub, u8 bssid_no, u8 *peer_addr, struct ieee80211_key_conf *key, u8 isvalid);

int sip_send_scan(struct esp_pub *epub);

void sip_scandone_process(struct esp_sip *sip, struct sip_evt_scan_report *scan_report);

int sip_send_bss_info_update(struct esp_pub *epub, struct esp_vif *evif, u8 *bssid, int assoc);

int  sip_send_wmm_params(struct esp_pub *epub, u8 aci, const struct ieee80211_tx_queue_params *params);

int sip_send_ampdu_action(struct esp_pub *epub, u8 action_num, const u8 * addr, u16 tid, u16 ssn, u8 buf_size);

int sip_send_roc(struct esp_pub *epub, u16 center_freq, u16 duration);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 28))
int sip_send_set_sta(struct esp_pub *epub, u8 ifidx, u8 set, struct ieee80211_sta *sta, struct ieee80211_vif *vif, u8 index);
#else
int sip_send_set_sta(struct esp_pub *epub, u8 ifidx, u8 set, struct esp_node *node,  struct ieee80211_vif *vif, u8 index);
#endif

int sip_send_suspend_config(struct esp_pub *epub, u8 suspend);

int sip_send_ps_config(struct esp_pub *epub, struct esp_ps *ps);

int sip_parse_events(struct esp_sip *sip, u8 *buf);

int sip_cmd(struct esp_pub *epub, enum sip_cmd_id cmd_id, u8 *cmd_buf, u8 cmd_len);

#ifdef ESP_RX_COPYBACK_TEST
int sip_show_copyback_buf(void);
#endif /* ESP_RX_COPYBACK_TEST */

#endif /* _ESP_CTRL_H_ */

