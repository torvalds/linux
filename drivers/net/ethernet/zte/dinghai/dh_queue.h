/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ZTE DingHai Ethernet driver - PCI capability definitions
 * Copyright (c) 2022-2026, ZTE Corporation.
 */

#ifndef __DH_QUEUE_H__
#define __DH_QUEUE_H__

#include <linux/types.h>

/* This is the PCI capability header: */
struct zxdh_pf_pci_cap {
	__u8 cap_vndr;		/* Generic PCI field: PCI_CAP_ID_VNDR */
	__u8 cap_next;		/* Generic PCI field: next ptr. */
	__u8 cap_len;		/* Generic PCI field: capability length */
	__u8 cfg_type;		/* Identifies the structure. */
	__u8 bar;		/* Where to find it. */
	__u8 id;		/* Multiple capabilities of the same type */
	__u8 padding[2];		/* Pad to full dword. */
	__le32 offset;		/* Offset within bar. */
	__le32 length;		/* Length of the structure, in bytes. */
};

/* Fields in ZXDH_PF_PCI_CAP_COMMON_CFG: */
struct zxdh_pf_pci_common_cfg {
	/* About the whole device. */
	__le32 device_feature_select; /* read-write */
	__le32 device_feature;	/* read-only */
	__le32 guest_feature_select; /* read-write */
	__le32 guest_feature;		/* read-write */
	__le16 msix_config;		/* read-write */
	__le16 num_queues;		/* read-only */
	__u8 device_status;		/* read-write */
	__u8 config_generation;	/* read-only */

	/* About a specific virtqueue. */
	__le16 queue_select;		/* read-write */
	__le16 queue_size;		/* read-write, power of 2. */
	__le16 queue_msix_vector;	/* read-write */
	__le16 queue_enable;		/* read-write */
	__le16 queue_notify_off;	/* read-only */
	__le32 queue_desc_lo;		/* read-write */
	__le32 queue_desc_hi;		/* read-write */
	__le32 queue_avail_lo;		/* read-write */
	__le32 queue_avail_hi;		/* read-write */
	__le32 queue_used_lo;		/* read-write */
	__le32 queue_used_hi;		/* read-write */
};

struct zxdh_pf_pci_notify_cap {
	struct zxdh_pf_pci_cap cap;
	__le32 notify_off_multiplier; /* Multiplier for queue_notify_off. */
};

#endif /* __DH_QUEUE_H__ */
