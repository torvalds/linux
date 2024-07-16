/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Author: Brijesh Singh <brijesh.singh@amd.com>
 *
 * SEV-SNP API spec is available at https://developer.amd.com/sev
 */

#ifndef __VIRT_SEVGUEST_H__
#define __VIRT_SEVGUEST_H__

#include <linux/types.h>

#define MAX_AUTHTAG_LEN		32

/* See SNP spec SNP_GUEST_REQUEST section for the structure */
enum msg_type {
	SNP_MSG_TYPE_INVALID = 0,
	SNP_MSG_CPUID_REQ,
	SNP_MSG_CPUID_RSP,
	SNP_MSG_KEY_REQ,
	SNP_MSG_KEY_RSP,
	SNP_MSG_REPORT_REQ,
	SNP_MSG_REPORT_RSP,
	SNP_MSG_EXPORT_REQ,
	SNP_MSG_EXPORT_RSP,
	SNP_MSG_IMPORT_REQ,
	SNP_MSG_IMPORT_RSP,
	SNP_MSG_ABSORB_REQ,
	SNP_MSG_ABSORB_RSP,
	SNP_MSG_VMRK_REQ,
	SNP_MSG_VMRK_RSP,

	SNP_MSG_TYPE_MAX
};

enum aead_algo {
	SNP_AEAD_INVALID,
	SNP_AEAD_AES_256_GCM,
};

struct snp_guest_msg_hdr {
	u8 authtag[MAX_AUTHTAG_LEN];
	u64 msg_seqno;
	u8 rsvd1[8];
	u8 algo;
	u8 hdr_version;
	u16 hdr_sz;
	u8 msg_type;
	u8 msg_version;
	u16 msg_sz;
	u32 rsvd2;
	u8 msg_vmpck;
	u8 rsvd3[35];
} __packed;

struct snp_guest_msg {
	struct snp_guest_msg_hdr hdr;
	u8 payload[4000];
} __packed;

#endif /* __VIRT_SEVGUEST_H__ */
