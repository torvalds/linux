/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef COMMON_H
#define COMMON_H

#include <linux/netdevice.h>

#define ATH6KL_MAX_IE			256

extern int ath6kl_printk(const char *level, const char *fmt, ...);

/*
 * Reflects the version of binary interface exposed by ATH6KL target
 * firmware. Needs to be incremented by 1 for any change in the firmware
 * that requires upgrade of the driver on the host side for the change to
 * work correctly
 */
#define ATH6KL_ABI_VERSION        1

#define SIGNAL_QUALITY_METRICS_NUM_MAX    2

enum {
	SIGNAL_QUALITY_METRICS_SNR = 0,
	SIGNAL_QUALITY_METRICS_RSSI,
	SIGNAL_QUALITY_METRICS_ALL,
};

/*
 * Data Path
 */

#define WMI_MAX_TX_DATA_FRAME_LENGTH	      \
	(1500 + sizeof(struct wmi_data_hdr) + \
	 sizeof(struct ethhdr) +      \
	 sizeof(struct ath6kl_llc_snap_hdr))

/* An AMSDU frame */ /* The MAX AMSDU length of AR6003 is 3839 */
#define WMI_MAX_AMSDU_RX_DATA_FRAME_LENGTH    \
	(3840 + sizeof(struct wmi_data_hdr) + \
	 sizeof(struct ethhdr) +      \
	 sizeof(struct ath6kl_llc_snap_hdr))

#define EPPING_ALIGNMENT_PAD			       \
	(((sizeof(struct htc_frame_hdr) + 3) & (~0x3)) \
	 - sizeof(struct htc_frame_hdr))

struct ath6kl_llc_snap_hdr {
	u8 dsap;
	u8 ssap;
	u8 cntl;
	u8 org_code[3];
	__be16 eth_type;
} __packed;

enum crypto_type {
	NONE_CRYPT          = 0x01,
	WEP_CRYPT           = 0x02,
	TKIP_CRYPT          = 0x04,
	AES_CRYPT           = 0x08,
	WAPI_CRYPT          = 0x10,
};

struct htc_endpoint_credit_dist;
struct ath6kl;
enum htc_credit_dist_reason;
struct ath6kl_htc_credit_info;

struct sk_buff *ath6kl_buf_alloc(int size);
#endif /* COMMON_H */
