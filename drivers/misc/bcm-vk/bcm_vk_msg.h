/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2018-2020 Broadcom.
 */

#ifndef BCM_VK_MSG_H
#define BCM_VK_MSG_H

/* context per session opening of sysfs */
struct bcm_vk_ctx {
	struct list_head node; /* use for linkage in Hash Table */
	unsigned int idx;
	bool in_use;
	pid_t pid;
	u32 hash_idx;
	struct miscdevice *miscdev;
};

/* pid hash table entry */
struct bcm_vk_ht_entry {
	struct list_head head;
};

/* total number of supported ctx, 32 ctx each for 5 components */
#define VK_CMPT_CTX_MAX		(32 * 5)

/* hash table defines to store the opened FDs */
#define VK_PID_HT_SHIFT_BIT	7 /* 128 */
#define VK_PID_HT_SZ		BIT(VK_PID_HT_SHIFT_BIT)

#endif
