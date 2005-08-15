#ifndef HOSTAP_H
#define HOSTAP_H

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
int hostap_80211_header_parse(struct sk_buff *skb, unsigned char *haddr);
int hostap_80211_prism_header_parse(struct sk_buff *skb, unsigned char *haddr);
int hostap_80211_get_hdrlen(u16 fc);
struct net_device_stats *hostap_get_stats(struct net_device *dev);
void hostap_setup_dev(struct net_device *dev, local_info_t *local,
		      int main_dev);
void hostap_set_multicast_list_queue(void *data);
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


/* hostap_proc.c */

void hostap_init_proc(local_info_t *local);
void hostap_remove_proc(local_info_t *local);


/* hostap_info.c */

void hostap_info_init(local_info_t *local);
void hostap_info_process(local_info_t *local, struct sk_buff *skb);


#endif /* HOSTAP_H */
