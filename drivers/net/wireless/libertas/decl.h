/**
  *  This file contains declaration referring to
  *  functions defined in other source files
  */

#ifndef _WLAN_DECL_H_
#define _WLAN_DECL_H_

#include "defs.h"

/** Function Prototype Declaration */
struct wlan_private;
struct sk_buff;
struct net_device;

extern char *libertas_fw_name;

void libertas_free_adapter(wlan_private * priv);
int libertas_set_mac_packet_filter(wlan_private * priv);

int libertas_send_null_packet(wlan_private * priv, u8 pwr_mgmt);
void libertas_send_tx_feedback(wlan_private * priv);
u8 libertas_check_last_packet_indication(wlan_private * priv);

int libertas_free_cmd_buffer(wlan_private * priv);
struct cmd_ctrl_node;
struct cmd_ctrl_node *libertas_get_free_cmd_ctrl_node(wlan_private * priv);

void libertas_set_cmd_ctrl_node(wlan_private * priv,
		    struct cmd_ctrl_node *ptempnode,
		    u32 cmd_oid, u16 wait_option, void *pdata_buf);

int libertas_prepare_and_send_command(wlan_private * priv,
			  u16 cmd_no,
			  u16 cmd_action,
			  u16 wait_option, u32 cmd_oid, void *pdata_buf);

void libertas_queue_cmd(wlan_adapter * adapter, struct cmd_ctrl_node *cmdnode, u8 addtail);

int libertas_allocate_cmd_buffer(wlan_private * priv);
int libertas_execute_next_command(wlan_private * priv);
int libertas_process_event(wlan_private * priv);
void libertas_interrupt(struct net_device *);
int libertas_set_radio_control(wlan_private * priv);
u32 libertas_index_to_data_rate(u8 index);
u8 libertas_data_rate_to_index(u32 rate);
void libertas_get_fwversion(wlan_adapter * adapter, char *fwversion, int maxlen);

void libertas_upload_rx_packet(wlan_private * priv, struct sk_buff *skb);

/** The proc fs interface */
int libertas_process_rx_command(wlan_private * priv);
int libertas_process_tx(wlan_private * priv, struct sk_buff *skb);
void libertas_cleanup_and_insert_cmd(wlan_private * priv,
					struct cmd_ctrl_node *ptempcmd);
void __libertas_cleanup_and_insert_cmd(wlan_private * priv,
					struct cmd_ctrl_node *ptempcmd);

int libertas_set_regiontable(wlan_private * priv, u8 region, u8 band);

int libertas_process_rxed_packet(wlan_private * priv, struct sk_buff *);

void libertas_ps_sleep(wlan_private * priv, int wait_option);
void libertas_ps_confirm_sleep(wlan_private * priv, u16 psmode);
void libertas_ps_wakeup(wlan_private * priv, int wait_option);

void libertas_tx_runqueue(wlan_private *priv);

extern struct chan_freq_power *libertas_find_cfp_by_band_and_channel(
				wlan_adapter * adapter, u8 band, u16 channel);

extern void libertas_mac_event_disconnected(wlan_private * priv);

void libertas_send_iwevcustom_event(wlan_private * priv, s8 * str);

int reset_device(wlan_private *priv);
/* main.c */
extern struct chan_freq_power *libertas_get_region_cfp_table(u8 region, u8 band,
						             int *cfp_no);
wlan_private *wlan_add_card(void *card);
int wlan_remove_card(void *card);

#endif				/* _WLAN_DECL_H_ */
