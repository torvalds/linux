/* Encapsulate basic setting changes on Hermes hardware
 *
 * See copyright notice in main.c
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
struct orinoco_private;
struct dev_addr_list;

int determine_fw_capabilities(struct orinoco_private *priv);
int orinoco_hw_read_card_settings(struct orinoco_private *priv, u8 *dev_addr);
int orinoco_hw_allocate_fid(struct orinoco_private *priv);
int orinoco_get_bitratemode(int bitrate, int automatic);
void orinoco_get_ratemode_cfg(int ratemode, int *bitrate, int *automatic);

int orinoco_hw_program_rids(struct orinoco_private *priv);
int orinoco_hw_get_tkip_iv(struct orinoco_private *priv, int key, u8 *tsc);
int __orinoco_hw_set_bitrate(struct orinoco_private *priv);
int orinoco_hw_get_act_bitrate(struct orinoco_private *priv, int *bitrate);
int __orinoco_hw_set_wap(struct orinoco_private *priv);
int __orinoco_hw_setup_wepkeys(struct orinoco_private *priv);
int __orinoco_hw_setup_enc(struct orinoco_private *priv);
int __orinoco_hw_set_tkip_key(struct orinoco_private *priv, int key_idx,
			      int set_tx, u8 *key, u8 *rsc, size_t rsc_len,
			      u8 *tsc, size_t tsc_len);
int orinoco_clear_tkip_key(struct orinoco_private *priv, int key_idx);
int __orinoco_hw_set_multicast_list(struct orinoco_private *priv,
				    struct dev_addr_list *mc_list,
				    int mc_count, int promisc);
int orinoco_hw_get_essid(struct orinoco_private *priv, int *active,
			 char buf[IW_ESSID_MAX_SIZE+1]);
int orinoco_hw_get_freq(struct orinoco_private *priv);
int orinoco_hw_get_bitratelist(struct orinoco_private *priv,
			       int *numrates, s32 *rates, int max);
int orinoco_hw_trigger_scan(struct orinoco_private *priv,
			    const struct cfg80211_ssid *ssid);
int orinoco_hw_disassociate(struct orinoco_private *priv,
			    u8 *addr, u16 reason_code);
int orinoco_hw_get_current_bssid(struct orinoco_private *priv,
				 u8 *addr);

#endif /* _ORINOCO_HW_H_ */
