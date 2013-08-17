
/*
 *  This file contains declaration referring to
 *  functions defined in other source files
 */

#ifndef _LBS_DECL_H_
#define _LBS_DECL_H_

#include <linux/netdevice.h>
#include <linux/firmware.h>
#include <linux/nl80211.h>

/* Should be terminated by a NULL entry */
struct lbs_fw_table {
	int model;
	const char *helper;
	const char *fwname;
};

struct lbs_private;
struct sk_buff;
struct net_device;
struct cmd_ds_command;


/* ethtool.c */
extern const struct ethtool_ops lbs_ethtool_ops;


/* tx.c */
void lbs_send_tx_feedback(struct lbs_private *priv, u32 try_count);
netdev_tx_t lbs_hard_start_xmit(struct sk_buff *skb,
				struct net_device *dev);

/* rx.c */
int lbs_process_rxed_packet(struct lbs_private *priv, struct sk_buff *);


/* main.c */
struct lbs_private *lbs_add_card(void *card, struct device *dmdev);
void lbs_remove_card(struct lbs_private *priv);
int lbs_start_card(struct lbs_private *priv);
void lbs_stop_card(struct lbs_private *priv);
void lbs_host_to_card_done(struct lbs_private *priv);

int lbs_start_iface(struct lbs_private *priv);
int lbs_stop_iface(struct lbs_private *priv);
int lbs_set_iface_type(struct lbs_private *priv, enum nl80211_iftype type);

int lbs_rtap_supported(struct lbs_private *priv);

int lbs_set_mac_address(struct net_device *dev, void *addr);
void lbs_set_multicast_list(struct net_device *dev);
void lbs_update_mcast(struct lbs_private *priv);

int lbs_suspend(struct lbs_private *priv);
int lbs_resume(struct lbs_private *priv);

void lbs_queue_event(struct lbs_private *priv, u32 event);
void lbs_notify_command_response(struct lbs_private *priv, u8 resp_idx);

int lbs_enter_auto_deep_sleep(struct lbs_private *priv);
int lbs_exit_auto_deep_sleep(struct lbs_private *priv);

u32 lbs_fw_index_to_data_rate(u8 index);
u8 lbs_data_rate_to_fw_index(u32 rate);

int lbs_get_firmware(struct device *dev, const char *user_helper,
			const char *user_mainfw, u32 card_model,
			const struct lbs_fw_table *fw_table,
			const struct firmware **helper,
			const struct firmware **mainfw);

#endif
