/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2024 Intel Corporation. */

#ifndef _IAVF_TYPES_H_
#define _IAVF_TYPES_H_

#include "iavf_types.h"

#include <linux/avf/virtchnl.h>
#include <linux/ptp_clock_kernel.h>

/* structure used to queue PTP commands for processing */
struct iavf_ptp_aq_cmd {
	struct list_head list;
	enum virtchnl_ops v_opcode:16;
	u16 msglen;
	u8 msg[] __counted_by(msglen);
};

struct iavf_ptp {
	wait_queue_head_t phc_time_waitqueue;
	struct virtchnl_ptp_caps hw_caps;
	struct ptp_clock_info info;
	struct ptp_clock *clock;
	struct list_head aq_cmds;
	u64 cached_phc_time;
	unsigned long cached_phc_updated;
	/* Lock protecting access to the AQ command list */
	struct mutex aq_cmd_lock;
	struct kernel_hwtstamp_config hwtstamp_config;
	bool phc_time_ready:1;
};

#endif /* _IAVF_TYPES_H_ */
