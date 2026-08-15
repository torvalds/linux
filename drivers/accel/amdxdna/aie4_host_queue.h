/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026, Advanced Micro Devices, Inc.
 */

#ifndef _AIE4_HOST_QUEUE_H_
#define _AIE4_HOST_QUEUE_H_

#include <linux/types.h>

#define CTX_MAX_CMDS                    32

struct host_queue_header {
	__u64 read_index;
	struct {
		__u16 major;
		__u16 minor;
	} version;
	__u32 capacity; /* Queue capacity, must be power of two. */
	__u64 write_index;
	__u64 data_address; /* The xdna dev addr for payload. */
};

#endif /* _AIE4_HOST_QUEUE_H_ */
