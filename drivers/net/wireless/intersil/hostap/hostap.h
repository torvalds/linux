/* SPDX-License-Identifier: GPL-2.0 */
#ifndef HOSTAP_H
#define HOSTAP_H

#include <linux/ethtool.h>
#include <linux/kernel.h>

#include "hostap_wlan.h"
#include "hostap_ap.h"

static const long freq_list[] = { 2412, 2417, 2422, 2427, 2432, 2437, 2442,
				  2447, 2452, 2457, 2462, 2467, 2472, 2484 };
#define FREQ_COUNT ARRAY_SIZE(freq_list)

/* hostap.c */

extern struct proc_dir_entry *hostap_proc;

u16 hostap_tx_callback_register(local_info_t *local,
				void (*func)(struct sk_buff *, int ok, void *),
				void *data);
int hostap_tx_callback_unregister(local_info_t *local, u16 idx);
int hostap_set_word(struct net_device *dev, int rid, u16 val);
int hostap_set_string(struct net_device *dev, int rid, const char *val);
u16 hostap_get_porttype(local_info_t *local);
int hostap_set_encryption(local_info_t *local);
int hostap_set_antsel(local_info_t *local);
int hostap_set_roaming(local_info_t *local);
int hostap_set_auth_algs(local_info_t *local);
void hostap_dump_rx_header(const char *name,
			   const struct hfa384x_rx_frame *rx);
void hostap_dump_tx_header(const char *name,
			   const struct hfa384x_tx_frame *tx);
extern const struct header_ops hostap_80211_ops;
int hostap_80211_get_hdrlen(__le16 fc);
struct net_device_stats *hostap_get_stats(struct net_device *dev);
void hostap_setup_dev(struct net_device *dev, local_info_t *local,
		      int type);
void hostap_set_multicast_list_queue(struct work_struct *work);
int hostap_set_hostapd(local_info_t *local, int val, int rtnl_locked);
int hostap_set_hostapd_sta(local_info_t *local, int val, int rtnl_locked);
void hostap_cleanup(local_info_t *local);
void hostap_cleanup_handler(void *data);
struct net_device * hostap_add_interface(struct local_info *local,
					 int type, int rtnl_locked,
					 const char *prefix, const char *name);
void hostap_remove_interface(struct net_device *dev, int rtnl_locked,
			     int remove_from_list);
int prism2_update_comms_qual(struct net_device *dev);
int prism2_sta_send_mgmt(local_info_t *local, u8 *dst, u16 stype,
			 u8 *body, size_t bodylen);
int prism2_sta_deauth(local_info_t *local, u16 reason);
int prism2_wds_add(local_info_t *local, u8 *remote_addr,
		   int rtnl_locked);
int prism2_wds_del(local_info_t *local, u8 *remote_addr,
		   int rtnl_locked, int do_not_remove);


/* hostap_ap.c */

int ap_control_add_mac(struct mac_restrictions *mac_restrictions, u8 *mac);
int ap_control_del_mac(struct mac_restrictions *mac_restrictions, u8 *mac);
void ap_control_flush_macs(struct mac_restrictions *mac_restrictions);
int ap_control_kick_mac(struct ap_data *ap, struct net_device *dev, u8 *mac);
void ap_control_kickall(struct ap_data *ap);
void * ap_crypt_get_ptrs(struct ap_data *ap, u8 *addr, int permanent,
			 struct lib80211_crypt_data ***crypt);
int prism2_ap_get_sta_qual(local_info_t *local, struct sockaddr addr[],
			   struct iw_quality qual[], int buf_size,
			   int aplist);
int prism2_ap_translate_scan(struct net_device *dev,
			     struct iw_request_info *info, char *buffer);
int prism2_hostapd(struct ap_data *ap, struct prism2_hostapd_param *param);


/* hostap_proc.c */

void hostap_init_proc(local_info_t *local);
void hostap_remove_proc(local_info_t *local);


/* hostap_info.c */

void hostap_info_init(local_info_t *local);
void hostap_info_process(local_info_t *local, struct sk_buff *skb);


/* hostap_ioctl.c */

extern const struct iw_handler_def hostap_iw_handler_def;
extern const struct ethtool_ops prism2_ethtool_ops;

int hostap_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);


#endif /* HOSTAP_H */
