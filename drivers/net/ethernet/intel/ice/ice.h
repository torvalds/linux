/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Intel Corporation. */

#ifndef _ICE_H_
#define _ICE_H_

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/compiler.h>
#include <linux/pci.h>
#include <linux/aer.h>
#include <linux/delay.h>
#include <linux/bitmap.h>
#include "ice_devids.h"
#include "ice_type.h"
#include "ice_switch.h"
#include "ice_common.h"
#include "ice_sched.h"

#define ICE_BAR0		0
#define ICE_AQ_LEN		64

#define ICE_DFLT_NETIF_M (NETIF_MSG_DRV | NETIF_MSG_PROBE | NETIF_MSG_LINK)

enum ice_state {
	__ICE_DOWN,
	__ICE_STATE_NBITS		/* must be last */
};

struct ice_pf {
	struct pci_dev *pdev;
	DECLARE_BITMAP(state, __ICE_STATE_NBITS);
	u32 msg_enable;
	struct ice_hw hw;
};
#endif /* _ICE_H_ */
