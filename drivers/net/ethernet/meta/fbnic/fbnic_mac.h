/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_MAC_H_
#define _FBNIC_MAC_H_

#include <linux/types.h>

struct fbnic_dev;

#define FBNIC_MAX_JUMBO_FRAME_SIZE	9742

/* This structure defines the interface hooks for the MAC. The MAC hooks
 * will be configured as a const struct provided with a set of function
 * pointers.
 *
 * void (*init_regs)(struct fbnic_dev *fbd);
 *	Initialize MAC registers to enable Tx/Rx paths and FIFOs.
 */
struct fbnic_mac {
	void (*init_regs)(struct fbnic_dev *fbd);
};

int fbnic_mac_init(struct fbnic_dev *fbd);
#endif /* _FBNIC_MAC_H_ */
