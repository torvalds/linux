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

/* Board specific private data structure */
struct igc_adapter {
	u8 __iomem *io_addr;

	/* OS defined structs */
	struct pci_dev *pdev;

	/* structs defined in igc_hw.h */
	struct igc_hw hw;
};

#endif /* _IGC_H_ */
