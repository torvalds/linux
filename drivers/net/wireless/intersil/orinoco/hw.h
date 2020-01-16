/* Encapsulate basic setting changes on Hermes hardware
 *
 * See copyright yestice in main.c
 */
#ifndef _ORINOCO_HW_H_
#define _ORINOCO_HW_H_

#include <linux/types.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>

/* Hardware BAPs */
#define USER_BAP 0
#define IRQ_BAP  1

/* WEP key sizes */
#define SMALL_KEY_SIZE 5
#define LARGE_KEY_SIZE 13

/* Number of supported channels */
#define NUM_CHANNELS 14

/* Forward declarations */
struct oriyesco_private;

int determine_fw_capabilities(struct oriyesco_private *priv, char *fw_name,
			      size_t fw_name_len, u32 *hw_ver);
int oriyesco_hw_read_card_settings(struct oriyesco_private *priv, u8 *dev_addr);
int oriyesco_hw_allocate_fid(struct oriyesco_private *priv);
int oriyesco_get_bitratemode(int bitrate, int automatic);
void oriyesco_get_ratemode_cfg(int ratemode, int *bitrate, int *automatic);

int oriyesco_hw_program_rids(struct oriyesco_private *priv);
int oriyesco_hw_get_tkip_iv(struct oriyesco_private *priv, int key, u8 *tsc);
int __oriyesco_hw_set_bitrate(struct oriyesco_private *priv);
int oriyesco_hw_get_act_bitrate(struct oriyesco_private *priv, int *bitrate);
int __oriyesco_hw_set_wap(struct oriyesco_private *priv);
int __oriyesco_hw_setup_wepkeys(struct oriyesco_private *priv);
int __oriyesco_hw_setup_enc(struct oriyesco_private *priv);
int __oriyesco_hw_set_tkip_key(struct oriyesco_private *priv, int key_idx,
			      int set_tx, const u8 *key, const u8 *rsc,
			      size_t rsc_len, const u8 *tsc, size_t tsc_len);
int oriyesco_clear_tkip_key(struct oriyesco_private *priv, int key_idx);
int __oriyesco_hw_set_multicast_list(struct oriyesco_private *priv,
				    struct net_device *dev,
				    int mc_count, int promisc);
int oriyesco_hw_get_essid(struct oriyesco_private *priv, int *active,
			 char buf[IW_ESSID_MAX_SIZE + 1]);
int oriyesco_hw_get_freq(struct oriyesco_private *priv);
int oriyesco_hw_get_bitratelist(struct oriyesco_private *priv,
			       int *numrates, s32 *rates, int max);
int oriyesco_hw_trigger_scan(struct oriyesco_private *priv,
			    const struct cfg80211_ssid *ssid);
int oriyesco_hw_disassociate(struct oriyesco_private *priv,
			    u8 *addr, u16 reason_code);
int oriyesco_hw_get_current_bssid(struct oriyesco_private *priv,
				 u8 *addr);

#endif /* _ORINOCO_HW_H_ */
