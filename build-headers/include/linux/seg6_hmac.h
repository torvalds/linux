/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_SEG6_HMAC_H
#define _LINUX_SEG6_HMAC_H

#include <linux/types.h>
#include <linux/seg6.h>

#define SEG6_HMAC_SECRET_LEN	64
#define SEG6_HMAC_FIELD_LEN	32

struct sr6_tlv_hmac {
	struct sr6_tlv tlvhdr;
	__u16 reserved;
	__be32 hmackeyid;
	__u8 hmac[SEG6_HMAC_FIELD_LEN];
};

enum {
	SEG6_HMAC_ALGO_SHA1 = 1,
	SEG6_HMAC_ALGO_SHA256 = 2,
};

#endif
