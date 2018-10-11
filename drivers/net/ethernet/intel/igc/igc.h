/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c)  2018 Intel Corporation */

#ifndef _IGC_H_
#define _IGC_H_

#include <linux/kobject.h>

#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/vmalloc.h>

#include <linux/ethtool.h>

#include <linux/sctp.h>

#define IGC_ERR(args...) pr_err("igc: " args)

#define PFX "igc: "

#include <linux/timecounter.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>

#include "igc_hw.h"

/* main */
extern char igc_driver_name[];
extern char igc_driver_version[];

/* Transmit and receive queues */
#define IGC_MAX_RX_QUEUES		4
#define IGC_MAX_TX_QUEUES		4

#define MAX_Q_VECTORS			8
#define MAX_STD_JUMBO_FRAME_SIZE	9216

enum igc_state_t {
	__IGC_TESTING,
	__IGC_RESETTING,
	__IGC_DOWN,
	__IGC_PTP_TX_IN_PROGRESS,
};

struct igc_q_vector {
	struct igc_adapter *adapter;    /* backlink */

	struct napi_struct napi;
};

struct igc_mac_addr {
	u8 addr[ETH_ALEN];
	u8 queue;
	u8 state; /* bitmask */
};

#define IGC_MAC_STATE_DEFAULT	0x1
#define IGC_MAC_STATE_MODIFIED	0x2
#define IGC_MAC_STATE_IN_USE	0x4

/* Board specific private data structure */
struct igc_adapter {
	struct net_device *netdev;

	unsigned long state;
	unsigned int flags;
	unsigned int num_q_vectors;
	u16 link_speed;
	u16 link_duplex;

	u8 port_num;

	u8 __iomem *io_addr;
	struct work_struct watchdog_task;

	int msg_enable;
	u32 max_frame_size;

	/* OS defined structs */
	struct pci_dev *pdev;

	/* structs defined in igc_hw.h */
	struct igc_hw hw;

	struct igc_q_vector *q_vector[MAX_Q_VECTORS];

	struct igc_mac_addr *mac_table;
};

#endif /* _IGC_H_ */
