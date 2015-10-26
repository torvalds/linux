/* QLogic qede NIC Driver
* Copyright (c) 2015 QLogic Corporation
*
* This software is available under the terms of the GNU General Public License
* (GPL) Version 2, available from the file COPYING in the main directory of
* this source tree.
*/

#ifndef _QEDE_H_
#define _QEDE_H_
#include <linux/compiler.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/netdevice.h>
#include <linux/interrupt.h>
#include <linux/bitmap.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/qed/common_hsi.h>
#include <linux/qed/eth_common.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_chain.h>
#include <linux/qed/qed_eth_if.h>

#define QEDE_MAJOR_VERSION		8
#define QEDE_MINOR_VERSION		4
#define QEDE_REVISION_VERSION		0
#define QEDE_ENGINEERING_VERSION	0
#define DRV_MODULE_VERSION __stringify(QEDE_MAJOR_VERSION) "."	\
		__stringify(QEDE_MINOR_VERSION) "."		\
		__stringify(QEDE_REVISION_VERSION) "."		\
		__stringify(QEDE_ENGINEERING_VERSION)

#define QEDE_ETH_INTERFACE_VERSION	300

#define DRV_MODULE_SYM		qede

struct qede_dev {
	struct qed_dev			*cdev;
	struct net_device		*ndev;
	struct pci_dev			*pdev;

	u32				dp_module;
	u8				dp_level;

	const struct qed_eth_ops	*ops;

	struct qed_dev_eth_info	dev_info;
#define QEDE_MAX_RSS_CNT(edev)	((edev)->dev_info.num_queues)
#define QEDE_MAX_TSS_CNT(edev)	((edev)->dev_info.num_queues * \
				 (edev)->dev_info.num_tc)

	u16				num_rss;
	u8				num_tc;
#define QEDE_RSS_CNT(edev)		((edev)->num_rss)
#define QEDE_TSS_CNT(edev)		((edev)->num_rss *	\
					 (edev)->num_tc)
#define QEDE_TSS_IDX(edev, txqidx)	((txqidx) % (edev)->num_rss)
#define QEDE_TC_IDX(edev, txqidx)	((txqidx) / (edev)->num_rss)

	struct qed_int_info		int_info;
	unsigned char			primary_mac[ETH_ALEN];

	/* Smaller private varaiant of the RTNL lock */
	struct mutex			qede_lock;
	u32				state; /* Protected by qede_lock */
};

/* Debug print definitions */
#define DP_NAME(edev) ((edev)->ndev->name)

#endif /* _QEDE_H_ */
