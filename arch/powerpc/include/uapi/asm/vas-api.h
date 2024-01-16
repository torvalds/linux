/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 * Copyright 2019 IBM Corp.
 */

#ifndef _UAPI_MISC_VAS_H
#define _UAPI_MISC_VAS_H

#include <linux/types.h>

#include <asm/ioctl.h>

#define VAS_MAGIC	'v'
#define VAS_TX_WIN_OPEN	_IOW(VAS_MAGIC, 0x20, struct vas_tx_win_open_attr)

/* Flags to VAS TX open window ioctl */
/* To allocate a window with QoS credit, otherwise use default credit */
#define VAS_TX_WIN_FLAG_QOS_CREDIT	0x0000000000000001

struct vas_tx_win_open_attr {
	__u32	version;
	__s16	vas_id;	/* specific instance of vas or -1 for default */
	__u16	reserved1;
	__u64	flags;
	__u64	reserved2[6];
};

#endif /* _UAPI_MISC_VAS_H */
