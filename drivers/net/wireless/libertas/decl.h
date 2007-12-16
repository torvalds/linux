/**
  *  This file contains declaration referring to
  *  functions defined in other source files
  */

#ifndef _LBS_DECL_H_
#define _LBS_DECL_H_

#include <linux/device.h>

#include "defs.h"

/** Function Prototype Declaration */
struct lbs_private;
struct sk_buff;
struct net_device;
struct cmd_ctrl_node;
struct cmd_ds_command;

int lbs_set_mac_packet_filter(struct lbs_private *priv);

void lbs_send_tx_feedback(struct lbs_private *priv);

int lbs_free_cmd_buffer(struct lbs_private *priv);

int lbs_prepare_and_send_command(struct lbs_private *priv,
	u16 cmd_no,
	u16 cmd_action,
	u16 wait_option, u32 cmd_oid, void *pdata_buf);

int lbs_allocate_cmd_buffer(struct lbs_private *priv);
int lbs_execute_next_command(struct lbs_private *priv);
int lbs_process_event(struct lbs_private *priv);
void lbs_interrupt(struct lbs_private *priv);
int lbs_set_radio_control(struct lbs_private *priv);
u32 lbs_fw_index_to_data_rate(u8 index);
u8 lbs_data_rate_to_fw_index(u32 rate);
void lbs_get_fwversion(struct lbs_private *priv,
	char *fwversion,
	int maxlen);

/** The proc fs interface */
int lbs_process_rx_command(struct lbs_private *priv);
void lbs_complete_command(struct lbs_private *priv, struct cmd_ctrl_node *cmd,
			  int result);
int lbs_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
int lbs_set_regiontable(struct lbs_private *priv, u8 region, u8 band);

int lbs_process_rxed_packet(struct lbs_private *priv, struct sk_buff *);

void lbs_ps_sleep(struct lbs_private *priv, int wait_option);
void lbs_ps_confirm_sleep(struct lbs_private *priv, u16 psmode);
void lbs_ps_wakeup(struct lbs_private *priv, int wait_option);

struct chan_freq_power *lbs_find_cfp_by_band_and_channel(
	struct lbs_private *priv,
	u8 band,
	u16 channel);

void lbs_mac_event_disconnected(struct lbs_private *priv);

void lbs_send_iwevcustom_event(struct lbs_private *priv, s8 *str);

/* main.c */
struct chan_freq_power *lbs_get_region_cfp_table(u8 region,
	u8 band,
	int *cfp_no);
struct lbs_private *lbs_add_card(void *card, struct device *dmdev);
int lbs_remove_card(struct lbs_private *priv);
int lbs_start_card(struct lbs_private *priv);
int lbs_stop_card(struct lbs_private *priv);
int lbs_reset_device(struct lbs_private *priv);
void lbs_host_to_card_done(struct lbs_private *priv);

int lbs_update_channel(struct lbs_private *priv);
#endif
