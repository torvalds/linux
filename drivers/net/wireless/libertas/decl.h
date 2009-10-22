/**
  *  This file contains declaration referring to
  *  functions defined in other source files
  */

#ifndef _LBS_DECL_H_
#define _LBS_DECL_H_

#include <linux/netdevice.h>

#include "defs.h"


extern const struct ethtool_ops lbs_ethtool_ops;

/** Function Prototype Declaration */
struct lbs_private;
struct sk_buff;
struct net_device;
struct cmd_ctrl_node;
struct cmd_ds_command;

int lbs_suspend(struct lbs_private *priv);
void lbs_resume(struct lbs_private *priv);

void lbs_send_tx_feedback(struct lbs_private *priv, u32 try_count);

void lbs_queue_event(struct lbs_private *priv, u32 event);
void lbs_notify_command_response(struct lbs_private *priv, u8 resp_idx);
int lbs_enter_auto_deep_sleep(struct lbs_private *priv);
int lbs_exit_auto_deep_sleep(struct lbs_private *priv);

u32 lbs_fw_index_to_data_rate(u8 index);
u8 lbs_data_rate_to_fw_index(u32 rate);

/** The proc fs interface */
netdev_tx_t lbs_hard_start_xmit(struct sk_buff *skb,
				struct net_device *dev);

int lbs_process_rxed_packet(struct lbs_private *priv, struct sk_buff *);

struct chan_freq_power *lbs_find_cfp_by_band_and_channel(
	struct lbs_private *priv,
	u8 band,
	u16 channel);

/* persistcfg.c */
void lbs_persist_config_init(struct net_device *net);
void lbs_persist_config_remove(struct net_device *net);

/* main.c */
struct lbs_private *lbs_add_card(void *card, struct device *dmdev);
void lbs_remove_card(struct lbs_private *priv);
int lbs_start_card(struct lbs_private *priv);
void lbs_stop_card(struct lbs_private *priv);
void lbs_host_to_card_done(struct lbs_private *priv);


#endif
