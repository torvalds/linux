/*
 * Copyright (c) 2016 Quantenna Communications, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef QLINK_COMMANDS_H_
#define QLINK_COMMANDS_H_

#include <linux/nl80211.h>

#include "core.h"
#include "bus.h"

int qtnf_cmd_send_init_fw(struct qtnf_bus *bus);
void qtnf_cmd_send_deinit_fw(struct qtnf_bus *bus);
int qtnf_cmd_get_hw_info(struct qtnf_bus *bus);
int qtnf_cmd_get_mac_info(struct qtnf_wmac *mac);
int qtnf_cmd_send_add_intf(struct qtnf_vif *vif, enum nl80211_iftype iftype,
			   u8 *mac_addr);
int qtnf_cmd_send_change_intf_type(struct qtnf_vif *vif,
				   enum nl80211_iftype iftype, u8 *mac_addr);
int qtnf_cmd_send_del_intf(struct qtnf_vif *vif);
int qtnf_cmd_get_mac_chan_info(struct qtnf_wmac *mac,
			       struct ieee80211_supported_band *band);
int qtnf_cmd_send_regulatory_config(struct qtnf_wmac *mac, const char *alpha2);
int qtnf_cmd_send_config_ap(struct qtnf_vif *vif);
int qtnf_cmd_send_start_ap(struct qtnf_vif *vif);
int qtnf_cmd_send_stop_ap(struct qtnf_vif *vif);
int qtnf_cmd_send_register_mgmt(struct qtnf_vif *vif, u16 frame_type, bool reg);
int qtnf_cmd_send_mgmt_frame(struct qtnf_vif *vif, u32 cookie, u16 flags,
			     u16 freq, const u8 *buf, size_t len);
int qtnf_cmd_send_mgmt_set_appie(struct qtnf_vif *vif, u8 frame_type,
				 const u8 *buf, size_t len);
int qtnf_cmd_get_sta_info(struct qtnf_vif *vif, const u8 *sta_mac,
			  struct station_info *sinfo);
int qtnf_cmd_send_phy_params(struct qtnf_wmac *mac, u16 cmd_action,
			     void *data_buf);
int qtnf_cmd_send_add_key(struct qtnf_vif *vif, u8 key_index, bool pairwise,
			  const u8 *mac_addr, struct key_params *params);
int qtnf_cmd_send_del_key(struct qtnf_vif *vif, u8 key_index, bool pairwise,
			  const u8 *mac_addr);
int qtnf_cmd_send_set_default_key(struct qtnf_vif *vif, u8 key_index,
				  bool unicast, bool multicast);
int qtnf_cmd_send_set_default_mgmt_key(struct qtnf_vif *vif, u8 key_index);
int qtnf_cmd_send_add_sta(struct qtnf_vif *vif, const u8 *mac,
			  struct station_parameters *params);
int qtnf_cmd_send_change_sta(struct qtnf_vif *vif, const u8 *mac,
			     struct station_parameters *params);
int qtnf_cmd_send_del_sta(struct qtnf_vif *vif,
			  struct station_del_parameters *params);

int qtnf_cmd_resp_parse(struct qtnf_bus *bus, struct sk_buff *resp_skb);
int qtnf_cmd_resp_check(const struct qtnf_vif *vif,
			const struct sk_buff *resp_skb, u16 cmd_id,
			u16 *result, const u8 **payload, size_t *payload_size);
int qtnf_cmd_send_scan(struct qtnf_wmac *mac);
int qtnf_cmd_send_connect(struct qtnf_vif *vif,
			  struct cfg80211_connect_params *sme);
int qtnf_cmd_send_disconnect(struct qtnf_vif *vif,
			     u16 reason_code);
int qtnf_cmd_send_updown_intf(struct qtnf_vif *vif,
			      bool up);
int qtnf_cmd_reg_notify(struct qtnf_bus *bus, struct regulatory_request *req);
int qtnf_cmd_get_chan_stats(struct qtnf_wmac *mac, u16 channel,
			    struct qtnf_chan_stats *stats);
int qtnf_cmd_send_chan_switch(struct qtnf_wmac *mac,
			      struct cfg80211_csa_settings *params);

#endif /* QLINK_COMMANDS_H_ */
