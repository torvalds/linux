/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "wcn36xx.h"

struct ftm_rsp_msg {
	u16 msg_id;
	u16 msg_body_length;
	u32 resp_status;
	u8 msg_response[0];
} __packed;

/* The request buffer of FTM which contains a byte of command and the request */
struct ftm_payload {
	u16 ftm_cmd_type;
	struct ftm_rsp_msg ftm_cmd_msg;
} __packed;

#define MSG_GET_BUILD_RELEASE_NUMBER 0x32A2

#ifdef CONFIG_NL80211_TESTMODE
int wcn36xx_tm_cmd(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
		   void *data, int len);

#else
static inline int wcn36xx_tm_cmd(struct ieee80211_hw *hw,
				 struct ieee80211_vif *vif,
				void *data, int len)
{
	return 0;
}

#endif
