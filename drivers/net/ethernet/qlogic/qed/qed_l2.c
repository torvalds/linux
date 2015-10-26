/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
 */

#include <linux/types.h>
#include <asm/byteorder.h>
#include <asm/param.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/etherdevice.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include "qed.h"
#include <linux/qed/qed_chain.h>
#include "qed_cxt.h"
#include "qed_dev_api.h"
#include <linux/qed/qed_eth_if.h>
#include "qed_hsi.h"
#include "qed_hw.h"
#include "qed_int.h"
#include "qed_reg_addr.h"
#include "qed_sp.h"

static int qed_fill_eth_dev_info(struct qed_dev *cdev,
				 struct qed_dev_eth_info *info)
{
	int i;

	memset(info, 0, sizeof(*info));

	info->num_tc = 1;

	if (cdev->int_params.out.int_mode == QED_INT_MODE_MSIX) {
		for_each_hwfn(cdev, i)
			info->num_queues += FEAT_NUM(&cdev->hwfns[i],
						     QED_PF_L2_QUE);
		if (cdev->int_params.fp_msix_cnt)
			info->num_queues = min_t(u8, info->num_queues,
						 cdev->int_params.fp_msix_cnt);
	} else {
		info->num_queues = cdev->num_hwfns;
	}

	info->num_vlan_filters = RESC_NUM(&cdev->hwfns[0], QED_VLAN);
	ether_addr_copy(info->port_mac,
			cdev->hwfns[0].hw_info.hw_mac_addr);

	qed_fill_dev_info(cdev, &info->common);

	return 0;
}

static const struct qed_eth_ops qed_eth_ops_pass = {
	.common = &qed_common_ops_pass,
	.fill_dev_info = &qed_fill_eth_dev_info,
};

const struct qed_eth_ops *qed_get_eth_ops(u32 version)
{
	if (version != QED_ETH_INTERFACE_VERSION) {
		pr_notice("Cannot supply ethtool operations [%08x != %08x]\n",
			  version, QED_ETH_INTERFACE_VERSION);
		return NULL;
	}

	return &qed_eth_ops_pass;
}
EXPORT_SYMBOL(qed_get_eth_ops);

void qed_put_eth_ops(void)
{
	/* TODO - reference count for module? */
}
EXPORT_SYMBOL(qed_put_eth_ops);
