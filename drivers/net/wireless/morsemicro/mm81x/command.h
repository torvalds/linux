/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_COMMAND_H_
#define _MM81X_COMMAND_H_

#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include "core.h"
#include "command_defs.h"

#define HOST_CMD_IS_REQ(cmd) (le16_to_cpu((cmd)->hdr.flags) & HOST_CMD_TYPE_REQ)
#define HOST_CMD_IS_RESP(cmd) \
	(le16_to_cpu((cmd)->hdr.flags) & HOST_CMD_TYPE_RESP)
#define HOST_CMD_IS_EVT(cmd) (le16_to_cpu((cmd)->hdr.flags) & HOST_CMD_TYPE_EVT)

struct mm81x_queue_params;

enum mm81x_cmd_return_code {
	MM81X_RET_SUCCESS = 0,
	MM81X_RET_EPERM = -1,
	MM81X_RET_ENOMEM = -12,
	MM81X_RET_CMD_NOT_HANDLED = -32757,
};

#define HOST_CMD_HOST_ID_SEQ_MAX 0xFFF
#define HOST_CMD_HOST_ID_RETRY_MASK 0x000F
#define HOST_CMD_HOST_ID_SEQ_SHIFT 4
#define HOST_CMD_HOST_ID_SEQ_MASK 0xFFF0

struct host_cmd_req {
	struct host_cmd_header hdr;
	u8 data[];
} __packed;

struct host_cmd_resp {
	struct host_cmd_header hdr;
	__le32 status;
	u8 data[];
} __packed;

struct host_cmd_event {
	struct host_cmd_header hdr;
	u8 data[];
} __packed;

int mm81x_cmd_resp_process(struct mm81x *mors, struct sk_buff *skb);
int mm81x_cmd_add_if(struct mm81x *mors, u16 *vif_id, const u8 *addr,
		     enum nl80211_iftype type);
int mm81x_cmd_get_capabilities(struct mm81x *mors, u16 vif_id,
			       struct mm81x_fw_caps *capabilities);
int mm81x_cmd_cfg_qos(struct mm81x *mors, struct mm81x_queue_params *params);
int mm81x_cmd_config_beacon_timer(struct mm81x *mors, void *mm81x_vif,
				  bool enabled);
int mm81x_cmd_cfg_bss(struct mm81x *mors, u16 vif_id, u16 beacon_int,
		      u16 dtim_period, u32 cssid);
int mm81x_cmd_set_channel(struct mm81x *mors, u32 op_chan_freq_hz,
			  u8 pri_1mhz_chan_idx, u8 op_bw_mhz, u8 pri_bw_mhz,
			  s32 *power_mbm);
int mm81x_cmd_get_max_txpower(struct mm81x *mors, s32 *out_power_mbm);
int mm81x_cmd_set_txpower(struct mm81x *mors, s32 *out_power_mbm,
			  int txpower_mbm);
int mm81x_cmd_hw_scan(struct mm81x *mors, struct mm81x_hw_scan_params *params,
		      bool store);
int mm81x_cmd_set_ps(struct mm81x *mors, bool enabled);
int mm81x_cmd_cfg_multicast_filter(struct mm81x *mors,
				   struct mm81x_vif *mors_vif);
int mm81x_cmd_sta_state(struct mm81x *mors, struct mm81x_vif *mors_vif, u16 aid,
			struct ieee80211_sta *sta,
			enum ieee80211_sta_state state);
int mm81x_cmd_install_key(struct mm81x *mors, struct mm81x_vif *mors_vif,
			  u16 aid, struct ieee80211_key_conf *key,
			  enum host_cmd_key_cipher cipher,
			  enum host_cmd_aes_key_len length);
int mm81x_cmd_disable_key(struct mm81x *mors, struct mm81x_vif *mors_vif,
			  u16 aid, struct ieee80211_key_conf *key);
int mm81x_cmd_rm_if(struct mm81x *mors, u16 vif_id);
int mm81x_cmd_set_frag_threshold(struct mm81x *mors, u32 frag_threshold);
int mm81x_cmd_get_disabled_channels(
	struct mm81x *mors, struct host_cmd_resp_get_disabled_channels *resp,
	uint resp_len);

#endif /* !_MM81X_COMMAND_H_ */
