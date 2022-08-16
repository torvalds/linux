/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 */

#ifndef __T7XX_PCI_RESCAN_H__
#define __T7XX_PCI_RESCAN_H__

#define MTK_RESCAN_WQ "mtk_rescan_wq"

#define DELAY_RESCAN_MTIME 1000
#define RESCAN_RETRIES 35

struct remove_rescan_context {
	struct work_struct	 service_task;
	struct workqueue_struct *pcie_rescan_wq;
	spinlock_t		dev_lock; /* protects device */
	struct pci_dev		*dev;
	int			rescan_done;
};

void t7xx_pci_dev_rescan(void);
void t7xx_rescan_queue_work(struct pci_dev *pdev);
int t7xx_rescan_init(void);
void t7xx_rescan_deinit(void);
void t7xx_rescan_done(void);

#endif	/* __T7XX_PCI_RESCAN_H__ */
